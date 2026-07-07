#include "NTERemoveBlurGenerator.h"

#include "Animation/AnimInstance.h"
#include "Components/PrimitiveComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "NTERemoveBlurGenerator"

namespace NTERemoveBlurGenerator
{
	static FString Trimmed(FString Value)
	{
		Value.TrimStartAndEndInline();
		Value.ReplaceInline(TEXT("\""), TEXT(""));
		return Value;
	}

	static FString NormalizeAssetPath(FString Path)
	{
		Path = Trimmed(Path);
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (Path.EndsWith(TEXT(".uasset")))
		{
			const int32 ContentIndex = Path.Find(TEXT("/Content/"), ESearchCase::IgnoreCase);
			if (ContentIndex != INDEX_NONE)
			{
				FString RelativePath = Path.Mid(ContentIndex + 9);
				RelativePath.LeftChopInline(7);
				return TEXT("/Game/") + RelativePath;
			}
		}

		if (Path.StartsWith(TEXT("Game/")))
		{
			Path = TEXT("/") + Path;
		}

		if (Path.StartsWith(TEXT("/Game/")) && !Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
		}

		return Path;
	}

	static UBlueprint* LoadBlueprint(const FString& RawPath, FNTERemoveBlurGeneratorResult& Result)
	{
		const FString Path = NormalizeAssetPath(RawPath);
		UObject* Object = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *Path);
		UBlueprint* Blueprint = Cast<UBlueprint>(Object);
		if (!Blueprint)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not load Animation Blueprint: %s"), *Path));
		}
		return Blueprint;
	}

	static bool HasVariable(UBlueprint* Blueprint, const FName VariableName)
	{
		if (!Blueprint)
		{
			return false;
		}

		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName) != INDEX_NONE)
		{
			return true;
		}

		return (Blueprint->SkeletonGeneratedClass && FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass, VariableName)) ||
			(Blueprint->GeneratedClass && FindFProperty<FProperty>(Blueprint->GeneratedClass, VariableName));
	}

	static bool EnsureMIDVariable(UBlueprint* Blueprint, const FName VariableName, FNTERemoveBlurGeneratorResult& Result)
	{
		if (!Blueprint)
		{
			return false;
		}

		const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName);
		if (VariableIndex != INDEX_NONE)
		{
			const FEdGraphPinType& ExistingType = Blueprint->NewVariables[VariableIndex].VarType;
			if (ExistingType.PinCategory != UEdGraphSchema_K2::PC_Object ||
				ExistingType.PinSubCategoryObject.Get() != UMaterialInstanceDynamic::StaticClass())
			{
				Result.Errors.Add(FString::Printf(TEXT("Variable exists with wrong type: %s"), *VariableName.ToString()));
				return false;
			}
			return true;
		}

		if (FObjectPropertyBase* ExistingProperty = Blueprint->SkeletonGeneratedClass ? FindFProperty<FObjectPropertyBase>(Blueprint->SkeletonGeneratedClass, VariableName) : nullptr)
		{
			if (!ExistingProperty->PropertyClass || !ExistingProperty->PropertyClass->IsChildOf(UMaterialInstanceDynamic::StaticClass()))
			{
				Result.Errors.Add(FString::Printf(TEXT("Variable exists with wrong type: %s"), *VariableName.ToString()));
				return false;
			}
			return true;
		}

		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UMaterialInstanceDynamic::StaticClass();

		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, PinType))
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to add MID variable: %s"), *VariableName.ToString()));
			return false;
		}

		Result.Messages.Add(FString::Printf(TEXT("Added MID variable: %s"), *VariableName.ToString()));
		return true;
	}

	static const UEdGraphSchema_K2* GetSchema()
	{
		return GetDefault<UEdGraphSchema_K2>();
	}

	static UEdGraphPin* FindPin(UEdGraphNode* Node, const FName PinName, const EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName == PinName && Pin->Direction == Direction)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	static UEdGraphPin* FindAnyPin(UEdGraphNode* Node, const FName PinName)
	{
		return Node ? Node->FindPin(PinName) : nullptr;
	}

	static UEdGraphPin* FindSelfPin(UEdGraphNode* Node)
	{
		if (UEdGraphPin* Pin = FindAnyPin(Node, UEdGraphSchema_K2::PN_Self))
		{
			return Pin;
		}
		return FindAnyPin(Node, TEXT("Target"));
	}

	static UEdGraphPin* FindSetOutputPin(UK2Node_VariableSet* Node)
	{
		if (!Node)
		{
			return nullptr;
		}

		if (UEdGraphPin* Pin = FindAnyPin(Node, TEXT("Output_Get")))
		{
			return Pin;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	static bool Connect(UEdGraphPin* A, UEdGraphPin* B, FNTERemoveBlurGeneratorResult& Result, const FString& Label)
	{
		if (!A || !B)
		{
			Result.Errors.Add(FString::Printf(TEXT("Missing pin while connecting: %s"), *Label));
			return false;
		}

		if (A->LinkedTo.Contains(B))
		{
			return true;
		}

		if (!GetSchema()->TryCreateConnection(A, B))
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to connect %s (%s -> %s)"), *Label, *A->PinName.ToString(), *B->PinName.ToString()));
			return false;
		}
		return true;
	}

	static void SetDefaultValue(UEdGraphPin* Pin, const FString& Value)
	{
		if (Pin)
		{
			GetSchema()->TrySetDefaultValue(*Pin, Value);
		}
	}

	static UK2Node_CallFunction* SpawnCall(UEdGraph* Graph, UClass* OwnerClass, const FName FunctionName, const FVector2D Position, FNTERemoveBlurGeneratorResult& Result)
	{
		UFunction* Function = OwnerClass ? OwnerClass->FindFunctionByName(FunctionName) : nullptr;
		if (!Function)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not find function %s.%s"), OwnerClass ? *OwnerClass->GetName() : TEXT("None"), *FunctionName.ToString()));
			return nullptr;
		}

		return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[Function](UK2Node_CallFunction* NewNode)
			{
				NewNode->SetFromFunction(Function);
			});
	}

	static UK2Node_VariableGet* SpawnVariableGet(UEdGraph* Graph, const FName VariableName, const FVector2D Position)
	{
		return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[VariableName](UK2Node_VariableGet* NewNode)
			{
				NewNode->VariableReference.SetSelfMember(VariableName);
			});
	}

	static UK2Node_VariableSet* SpawnVariableSet(UEdGraph* Graph, const FName VariableName, const FVector2D Position)
	{
		return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableSet>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[VariableName](UK2Node_VariableSet* NewNode)
			{
				NewNode->VariableReference.SetSelfMember(VariableName);
			});
	}

	static UK2Node_Event* FindEventNode(UEdGraph* Graph, const FName EventName)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
			if (EventNode && EventNode->GetFunctionName() == EventName)
			{
				return EventNode;
			}
		}
		return nullptr;
	}

	static UK2Node_Event* FindOrCreateAnimEvent(UEdGraph* Graph, const FName EventName, const FVector2D Position, FNTERemoveBlurGeneratorResult& Result)
	{
		if (UK2Node_Event* ExistingEvent = FindEventNode(Graph, EventName))
		{
			return ExistingEvent;
		}

		UK2Node_Event* EventNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_Event>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[EventName](UK2Node_Event* NewNode)
			{
				NewNode->EventReference.SetExternalMember(EventName, UAnimInstance::StaticClass());
				NewNode->bOverrideFunction = true;
			});

		if (!EventNode)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not create event node: %s"), *EventName.ToString()));
		}
		return EventNode;
	}

	static int32 CountSequenceOutputs(UK2Node_ExecutionSequence* Sequence)
	{
		int32 Count = 0;
		if (!Sequence)
		{
			return Count;
		}

		for (UEdGraphPin* Pin : Sequence->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && UEdGraphSchema_K2::IsExecPin(*Pin))
			{
				++Count;
			}
		}
		return Count;
	}

	static UEdGraphPin* FindAvailableThenPin(UK2Node_ExecutionSequence* Sequence)
	{
		if (!Sequence)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Sequence->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && UEdGraphSchema_K2::IsExecPin(*Pin) && Pin->LinkedTo.Num() == 0)
			{
				return Pin;
			}
		}

		const int32 OldCount = CountSequenceOutputs(Sequence);
		Sequence->AddInputPin();
		return Sequence->GetThenPinGivenIndex(OldCount);
	}

	static UEdGraphPin* GetEventExecutionEntry(UEdGraph* Graph, const FName EventName, const FVector2D EventPosition, FNTERemoveBlurGeneratorResult& Result)
	{
		UK2Node_Event* EventNode = FindOrCreateAnimEvent(Graph, EventName, EventPosition, Result);
		if (!EventNode)
		{
			return nullptr;
		}

		UEdGraphPin* EventOutput = GetSchema()->FindExecutionPin(*EventNode, EGPD_Output);
		if (!EventOutput)
		{
			Result.Errors.Add(FString::Printf(TEXT("Event has no execution output: %s"), *EventName.ToString()));
			return nullptr;
		}

		UK2Node_ExecutionSequence* Sequence = nullptr;
		for (UEdGraphPin* LinkedPin : EventOutput->LinkedTo)
		{
			if (LinkedPin && LinkedPin->Direction == EGPD_Input)
			{
				Sequence = Cast<UK2Node_ExecutionSequence>(LinkedPin->GetOwningNode());
				if (Sequence)
				{
					break;
				}
			}
		}

		if (!Sequence)
		{
			TArray<UEdGraphPin*> OldLinks = EventOutput->LinkedTo;
			EventOutput->BreakAllPinLinks();

			Sequence = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ExecutionSequence>(
				Graph,
				FVector2D(EventNode->NodePosX + 260, EventNode->NodePosY),
				EK2NewNodeFlags::None);

			Connect(EventOutput, FindPin(Sequence, UEdGraphSchema_K2::PN_Execute, EGPD_Input), Result, TEXT("Event -> Sequence"));
			UEdGraphPin* PreservePin = Sequence->GetThenPinGivenIndex(0);
			for (UEdGraphPin* OldLink : OldLinks)
			{
				if (OldLink)
				{
					Connect(PreservePin, OldLink, Result, TEXT("Preserve existing event flow"));
				}
			}
		}

		return FindAvailableThenPin(Sequence);
	}

	static int32 ApproxNodeHeight(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return 0;
		}
		if (Node->NodeHeight > 0)
		{
			return Node->NodeHeight;
		}
		return Node->IsA<UEdGraphNode_Comment>() ? 1200 : 260;
	}

	static int32 FindNextLayoutBaseY(UEdGraph* Graph)
	{
		constexpr int32 DefaultBaseY = 1200;
		if (!Graph)
		{
			return DefaultBaseY;
		}

		int32 MaxBottom = MIN_int32;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node)
			{
				MaxBottom = FMath::Max(MaxBottom, Node->NodePosY + ApproxNodeHeight(Node));
			}
		}
		return MaxBottom == MIN_int32 ? DefaultBaseY : FMath::Max(DefaultBaseY, ((MaxBottom + 700 + 399) / 400) * 400);
	}

	static UEdGraphNode_Comment* SpawnCommentBox(UEdGraph* Graph, const FString& Label, const FVector2D Position, const FVector2D Size, const FLinearColor Color)
	{
		UEdGraphNode_Comment* CommentTemplate = NewObject<UEdGraphNode_Comment>();
		UEdGraphNode_Comment* Comment = FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UEdGraphNode_Comment>(Graph, CommentTemplate, Position, false);
		if (!Comment)
		{
			return nullptr;
		}

		Comment->NodeComment = Label;
		Comment->NodeWidth = FMath::RoundToInt(Size.X);
		Comment->NodeHeight = FMath::RoundToInt(Size.Y);
		Comment->CommentColor = Color;
		Comment->FontSize = 18;
		Comment->MoveMode = ECommentBoxMode::GroupMovement;
		Comment->CommentDepth = -1;
		Comment->bCommentBubbleVisible = true;
		Comment->bCommentBubbleVisible_InDetailsPanel = true;
		return Comment;
	}

	static void AddNodesToComment(UEdGraphNode_Comment* Comment, const TArray<UEdGraphNode*>& Nodes)
	{
		if (!Comment)
		{
			return;
		}

		for (UEdGraphNode* Node : Nodes)
		{
			if (Node)
			{
				Comment->AddNodeUnderComment(Node);
			}
		}
	}

	static FString FloatDefault(const float Value)
	{
		return FString::SanitizeFloat(Value);
	}

	static UEdGraphPin* AddScalarOverrideNodes(
		UEdGraph* Graph,
		const TArray<FNTERemoveBlurScalarOverride>& Overrides,
		UEdGraphPin* EntryExec,
		UEdGraphPin* MIDPin,
		const FVector2D BasePosition,
		FNTERemoveBlurGeneratorResult& Result,
		TArray<UEdGraphNode*>& OutNodes)
	{
		UEdGraphPin* LastExec = EntryExec;
		for (int32 Index = 0; Index < Overrides.Num(); ++Index)
		{
			const FNTERemoveBlurScalarOverride& Override = Overrides[Index];
			UK2Node_CallFunction* SetScalar = SpawnCall(
				Graph,
				UMaterialInstanceDynamic::StaticClass(),
				TEXT("SetScalarParameterValue"),
				BasePosition + FVector2D((Index % 4) * 360.0f, (Index / 4) * 220.0f),
				Result);
			if (!SetScalar)
			{
				return LastExec;
			}

			SetDefaultValue(FindAnyPin(SetScalar, TEXT("ParameterName")), Override.ParameterName.ToString());
			SetDefaultValue(FindAnyPin(SetScalar, TEXT("Value")), FloatDefault(Override.Value));
			Connect(LastExec, GetSchema()->FindExecutionPin(*SetScalar, EGPD_Input), Result, FString::Printf(TEXT("Set %s exec"), *Override.ParameterName.ToString()));
			Connect(MIDPin, FindSelfPin(SetScalar), Result, FString::Printf(TEXT("MID -> Set %s"), *Override.ParameterName.ToString()));
			LastExec = GetSchema()->FindExecutionPin(*SetScalar, EGPD_Output);
			OutNodes.Add(SetScalar);
		}
		return LastExec;
	}

	static UK2Node_ExecutionSequence* SpawnSequence(UEdGraph* Graph, const FVector2D Position, const int32 ThenCount)
	{
		UK2Node_ExecutionSequence* Sequence = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ExecutionSequence>(
			Graph,
			Position,
			EK2NewNodeFlags::None);

		while (Sequence && CountSequenceOutputs(Sequence) < ThenCount)
		{
			Sequence->AddInputPin();
		}
		return Sequence;
	}

	static TArray<FName> BuildMIDVariableNames(const TArray<int32>& MaterialElementIndices)
	{
		TArray<FName> Names;
		Names.Reserve(MaterialElementIndices.Num());
		for (const int32 MaterialElementIndex : MaterialElementIndices)
		{
			Names.Add(FName(*FString::Printf(TEXT("NTERemoveBlurMID_%d"), MaterialElementIndex)));
		}
		return Names;
	}

	static void GenerateInitializeGraph(
		UBlueprint* AnimBlueprint,
		const TArray<int32>& MaterialElementIndices,
		const TArray<FName>& MIDVariables,
		const TArray<FNTERemoveBlurScalarOverride>& ScalarOverrides,
		const int32 BaseY,
		FNTERemoveBlurGeneratorResult& Result)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
		if (!Graph)
		{
			Result.Errors.Add(TEXT("Animation Blueprint has no EventGraph."));
			return;
		}

		UEdGraphPin* EntryExec = GetEventExecutionEntry(Graph, TEXT("BlueprintInitializeAnimation"), FVector2D(-1300, BaseY), Result);
		if (!EntryExec)
		{
			return;
		}

		UK2Node_ExecutionSequence* SlotSequence = SpawnSequence(Graph, FVector2D(-900, BaseY), MaterialElementIndices.Num());
		UK2Node_CallFunction* GetOwning = SpawnCall(Graph, UAnimInstance::StaticClass(), TEXT("GetOwningComponent"), FVector2D(-900, BaseY + 280), Result);
		if (!SlotSequence || !GetOwning)
		{
			return;
		}

		Connect(EntryExec, FindPin(SlotSequence, UEdGraphSchema_K2::PN_Execute, EGPD_Input), Result, TEXT("Init entry -> Slot sequence"));

		TArray<UEdGraphNode*> CommentNodes = { SlotSequence, GetOwning };
		for (int32 SlotIndex = 0; SlotIndex < MaterialElementIndices.Num(); ++SlotIndex)
		{
			const float SlotBaseY = BaseY + SlotIndex * 760.0f;
			UK2Node_CallFunction* CreateMID = SpawnCall(Graph, UPrimitiveComponent::StaticClass(), TEXT("CreateDynamicMaterialInstance"), FVector2D(-520, SlotBaseY), Result);
			UK2Node_VariableSet* SetMID = SpawnVariableSet(Graph, MIDVariables[SlotIndex], FVector2D(-120, SlotBaseY));
			if (!CreateMID || !SetMID)
			{
				return;
			}

			SetDefaultValue(FindAnyPin(CreateMID, TEXT("ElementIndex")), FString::FromInt(MaterialElementIndices[SlotIndex]));
			Connect(SlotSequence->GetThenPinGivenIndex(SlotIndex), GetSchema()->FindExecutionPin(*CreateMID, EGPD_Input), Result, FString::Printf(TEXT("Init slot %d"), MaterialElementIndices[SlotIndex]));
			Connect(FindAnyPin(GetOwning, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(CreateMID), Result, TEXT("Owning Component -> Create MID"));
			Connect(FindAnyPin(CreateMID, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SetMID, MIDVariables[SlotIndex]), Result, TEXT("Create MID -> Set variable"));
			Connect(GetSchema()->FindExecutionPin(*CreateMID, EGPD_Output), GetSchema()->FindExecutionPin(*SetMID, EGPD_Input), Result, TEXT("Create MID -> Set variable exec"));

			CommentNodes.Add(CreateMID);
			CommentNodes.Add(SetMID);
			AddScalarOverrideNodes(
				Graph,
				ScalarOverrides,
				GetSchema()->FindExecutionPin(*SetMID, EGPD_Output),
				FindSetOutputPin(SetMID),
				FVector2D(260, SlotBaseY),
				Result,
				CommentNodes);
		}

		UEdGraphNode_Comment* Comment = SpawnCommentBox(
			Graph,
			TEXT("NTE Remove Blur - Initialize MIDs"),
			FVector2D(-1380, BaseY - 180),
			FVector2D(2200, FMath::Max(900.0f, MaterialElementIndices.Num() * 760.0f + 420.0f)),
			FLinearColor(0.12f, 0.18f, 0.34f, 1.0f));
		AddNodesToComment(Comment, CommentNodes);
	}

	static void GenerateUpdateGraph(
		UBlueprint* AnimBlueprint,
		const TArray<int32>& MaterialElementIndices,
		const TArray<FName>& MIDVariables,
		const TArray<FNTERemoveBlurScalarOverride>& ScalarOverrides,
		const int32 BaseY,
		FNTERemoveBlurGeneratorResult& Result)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
		if (!Graph)
		{
			Result.Errors.Add(TEXT("Animation Blueprint has no EventGraph."));
			return;
		}

		UEdGraphPin* EntryExec = GetEventExecutionEntry(Graph, TEXT("BlueprintUpdateAnimation"), FVector2D(-1300, BaseY), Result);
		if (!EntryExec)
		{
			return;
		}

		UK2Node_ExecutionSequence* SlotSequence = SpawnSequence(Graph, FVector2D(-900, BaseY), MaterialElementIndices.Num());
		if (!SlotSequence)
		{
			return;
		}

		Connect(EntryExec, FindPin(SlotSequence, UEdGraphSchema_K2::PN_Execute, EGPD_Input), Result, TEXT("Update entry -> Slot sequence"));

		TArray<UEdGraphNode*> CommentNodes = { SlotSequence };
		for (int32 SlotIndex = 0; SlotIndex < MaterialElementIndices.Num(); ++SlotIndex)
		{
			const float SlotBaseY = BaseY + SlotIndex * 760.0f;
			UK2Node_VariableGet* GetMID = SpawnVariableGet(Graph, MIDVariables[SlotIndex], FVector2D(-520, SlotBaseY + 80.0f));
			if (!GetMID)
			{
				Result.Errors.Add(FString::Printf(TEXT("Failed to create MID getter: %s"), *MIDVariables[SlotIndex].ToString()));
				return;
			}

			CommentNodes.Add(GetMID);
			AddScalarOverrideNodes(
				Graph,
				ScalarOverrides,
				SlotSequence->GetThenPinGivenIndex(SlotIndex),
				FindAnyPin(GetMID, MIDVariables[SlotIndex]),
				FVector2D(-120, SlotBaseY),
				Result,
				CommentNodes);
		}

		UEdGraphNode_Comment* Comment = SpawnCommentBox(
			Graph,
			TEXT("NTE Remove Blur - Reapply parameters"),
			FVector2D(-980, BaseY - 180),
			FVector2D(2200, FMath::Max(900.0f, MaterialElementIndices.Num() * 760.0f + 420.0f)),
			FLinearColor(0.12f, 0.28f, 0.18f, 1.0f));
		AddNodesToComment(Comment, CommentNodes);
	}
}

TArray<FNTERemoveBlurScalarOverride> FNTERemoveBlurGenerator::MakeDefaultScalarOverrides()
{
	return {
		{ FName(TEXT("FPSCameFade")), 0.0f },
		{ FName(TEXT("FPSCameraCutHeight")), -99999.0f },
		{ FName(TEXT("FPSCameraCutHeightOverride")), -99999.0f },
		{ FName(TEXT("PlayerDitherRandom")), 0.0f },
		{ FName(TEXT("OpacityMaskVal")), 1.0f },
		{ FName(TEXT("MaskOpacity")), 1.0f },
		{ FName(TEXT("InvisableSkillOpacity")), 1.0f },
		{ FName(TEXT("PlayerInvisable")), 1.0f },
		{ FName(TEXT("Opacity")), 1.0f },
		{ FName(TEXT("Dissolve")), 0.0f },
		{ FName(TEXT("DissolveSkill")), 0.0f }
	};
}

FString FNTERemoveBlurGeneratorResult::ToDisplayString() const
{
	TArray<FString> Lines;
	for (const FString& Error : Errors)
	{
		Lines.Add(TEXT("Error: ") + Error);
	}
	for (const FString& Message : Messages)
	{
		Lines.Add(Message);
	}
	if (Lines.Num() == 0)
	{
		Lines.Add(bSuccess ? TEXT("Done.") : TEXT("No output."));
	}
	return FString::Join(Lines, TEXT("\n"));
}

FNTERemoveBlurGeneratorResult FNTERemoveBlurGenerator::Generate(const FNTERemoveBlurGeneratorParams& Params)
{
	using namespace NTERemoveBlurGenerator;

	FNTERemoveBlurGeneratorResult Result;
	const FScopedTransaction Transaction(LOCTEXT("GenerateRemoveBlurNodes", "Generate NTE Remove Blur Nodes"));

	UBlueprint* AnimBlueprint = LoadBlueprint(Params.AnimBlueprintPath, Result);
	if (!AnimBlueprint)
	{
		return Result;
	}

	if (Params.MaterialElementIndices.Num() == 0)
	{
		Result.Errors.Add(TEXT("Enter at least one Material Slot."));
		return Result;
	}

	if (!Params.bGenerateInitializeGraph && !Params.bGenerateUpdateGraph)
	{
		Result.Errors.Add(TEXT("Enable Initialize graph or Update graph."));
		return Result;
	}

	for (const int32 MaterialElementIndex : Params.MaterialElementIndices)
	{
		if (MaterialElementIndex < 0)
		{
			Result.Errors.Add(TEXT("Material Slot must be zero or greater."));
			return Result;
		}
	}

	TArray<FNTERemoveBlurScalarOverride> ScalarOverrides = Params.ScalarOverrides;
	if (ScalarOverrides.Num() == 0)
	{
		ScalarOverrides = MakeDefaultScalarOverrides();
	}

	AnimBlueprint->Modify();
	TArray<int32> UniqueMaterialSlots;
	for (const int32 MaterialElementIndex : Params.MaterialElementIndices)
	{
		UniqueMaterialSlots.AddUnique(MaterialElementIndex);
	}
	UniqueMaterialSlots.Sort();

	const TArray<FName> MIDVariables = BuildMIDVariableNames(UniqueMaterialSlots);
	for (const FName MIDVariable : MIDVariables)
	{
		if (!EnsureMIDVariable(AnimBlueprint, MIDVariable, Result))
		{
			return Result;
		}
	}

	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);

	UEdGraph* LayoutGraph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
	int32 NextLayoutBaseY = FindNextLayoutBaseY(LayoutGraph);

	if (Params.bGenerateInitializeGraph)
	{
		GenerateInitializeGraph(AnimBlueprint, UniqueMaterialSlots, MIDVariables, ScalarOverrides, NextLayoutBaseY, Result);
		NextLayoutBaseY += FMath::Max(1200, UniqueMaterialSlots.Num() * 760 + 600);
	}

	if (Params.bGenerateUpdateGraph)
	{
		GenerateUpdateGraph(AnimBlueprint, UniqueMaterialSlots, MIDVariables, ScalarOverrides, NextLayoutBaseY, Result);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (AnimBlueprint->Status == BS_Error)
	{
		Result.Errors.Add(TEXT("The Animation Blueprint has compile errors after generation."));
	}

	if (Params.bSaveAssets)
	{
		TArray<UPackage*> Packages;
		Packages.AddUnique(AnimBlueprint->GetOutermost());
		TArray<UPackage*> FailedPackages;
		FEditorFileUtils::PromptForCheckoutAndSave(Packages, false, false, &FailedPackages, true, false);
		if (FailedPackages.Num() > 0)
		{
			Result.Errors.Add(TEXT("Nodes were generated, but saving the asset failed. Save it manually in the editor."));
		}
		else
		{
			Result.Messages.Add(TEXT("Asset saved."));
		}
	}

	Result.Messages.Add(FString::Printf(TEXT("Generated remove-blur nodes for %d material slot(s)."), UniqueMaterialSlots.Num()));
	Result.bSuccess = Result.Errors.Num() == 0;
	return Result;
}

#undef LOCTEXT_NAMESPACE
